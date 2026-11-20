// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_weapon_visual.hpp"
#include <cmath>

namespace revolverator {
NativeWeaponVisual::NativeWeaponVisual(const openrac_api* api,GuestArena& level_arena,GuestArena& held_arena,openrac_mod* mod)
    :Memory(api),alias_(api),held_(api,held_arena),standalone_(api,level_arena,true),
     equipped_(api,level_arena,true,true),acquisition_(api,level_arena,true,true),valve_smoke_(api),
     vent_heat_(api,mod,"assets/vent-heat/slots.glb"),
     tip_heat_{{{api,mod,"assets/tip-heat/band-1.glb"},{api,mod,"assets/tip-heat/band-2.glb"},
                {api,mod,"assets/tip-heat/band-3.glb"},{api,mod,"assets/tip-heat/band-4.glb"}}}{}

void NativeWeaponVisual::begin_native_frame(){alias_.frame(true);}
void NativeWeaponVisual::restore_native(){alias_.frame();}
void NativeWeaponVisual::set_mode(int mode){
    if(mode!=3){standalone_.reset();equipped_.reset();}
    if(mode!=5)acquisition_.reset();
}
NativeRotor& NativeWeaponVisual::preview(Preview context){
    switch(context){
    case Preview::Standalone:return standalone_;
    case Preview::Equipped:return equipped_;
    case Preview::Acquisition:return acquisition_;
    }
    return standalone_;
}
void NativeWeaponVisual::prepare_native(unsigned actor){
    if(valid(actor,256)&&alias_.owns(actor))
        write<unsigned short>(actor+0xa6,NativeWeaponAlias::source_class);
}
NativeWeaponVisual::Identity NativeWeaponVisual::identity(unsigned actor)const{
    if(!valid(actor,256))return {};
    return {actor,read<unsigned>(actor+0x24),read<unsigned>(actor+0x78),read<unsigned>(actor+0x38)};
}
void NativeWeaponVisual::clear_held(){
    held_.reset();assembly_.reset();held_identity_={};equip_pending_=live_=logical_equipped_=pause_continuation_=false;
    valve_cycle_.reset();valve_frame_={};
    heating_=false;tip_glow_={};update_glow(false);
}
void NativeWeaponVisual::update_held(const HeldState& state){
    const auto& frame=state.frame;
    valve_frame_.puffs=0;
    heating_=false;
    tip_glow_={};
    if(!state.logical_equipped){clear_held();alias_.apply(state.actor,false);return;}
    logical_equipped_=true;
    // Deep native weapon menus may temporarily retire the world item or put
    // a class0 preview/body in its slot. That is not a logical weapon switch.
    // Keep the semantic equip and paused envelope until the real item returns.
    if(frame.paused&&held_identity_.actor)pause_continuation_=true;
    const bool native_actor=valid(state.actor,256)&&
        (read<unsigned short>(state.actor+0xa6)==NativeWeaponAlias::source_class||alias_.owns(state.actor));
    if(!frame.equipped||!native_actor){
        live_=false;
        update_glow(false);
        if(!frame.paused&&!pause_continuation_){clear_held();logical_equipped_=true;}
        alias_.apply(state.actor,false);return;
    }
    live_=frame.playing&&!frame.paused;
    // A native item callback can publish the render identity before the first
    // visual frame ever attaches its rotor. Canonicalize only our owned actor
    // for that pose boundary; otherwise an initial2017/2018 would permanently
    // prevent NativeRotor's deliberately strict native180 attachment.
    prepare_native(state.actor);
    struct Republish {
        NativeWeaponVisual& owner;unsigned actor;bool golden;
        ~Republish(){try{owner.publish_held(actor,true,golden);}catch(...){} }
    } render{*this,state.actor,state.golden};
    held_.update(state.actor,state.ammunition,state.phase,state.charge,state.enemy_angle,
                 frame.seconds,frame.playing,frame.paused);
    if(held_.owns(state.actor)){
        const auto current=identity(state.actor);
        if(!(current==held_identity_)){
            const bool continued=pause_continuation_&&held_identity_.actor;
            held_identity_=current;
            if(!continued){assembly_.begin(state.ammunition>0);valve_cycle_.reset();equip_pending_=true;}
        }
        assembly_.update({frame.seconds,frame.playing,frame.paused,state.phase!=Phase::Idle,state.ammunition>0});
        const auto assembly=assembly_.pose();
        const auto& motor=held_.controller();
        const bool powered=live_&&frame.alive&&assembly.housing_scale>.9f&&!motor.dry_firing()&&
            (state.ammunition>0||holding_enemy(state.phase)||state.phase==Phase::Windup);
        valve_frame_=valve_cycle_.update(frame.seconds,motor.speed(),powered,frame.paused);
        heating_=powered&&state.ammunition>0&&std::abs(motor.speed())>=.3f;
        if(live_&&frame.alive)tip_glow_=state.tip_heat;
        held_.set_assembly_pose(assembly);held_.set_valve_scale(valve_frame_.scale);held_.apply();
        if(live_)pause_continuation_=false;
    }
    // Every return restores the logical model, including the playing frame in
    // which native code may enter pause and omit the item's next update. The
    // rotor itself freezes its angle and activation while paused.
}
bool NativeWeaponVisual::publish_held(unsigned actor,bool custom,bool golden){
    if(!custom){
        heating_=false;tip_glow_={};update_glow(false);
        if(logical_equipped_&&pause_continuation_)held_.reset();
        else clear_held();
        return alias_.apply(actor,false);
    }
    held_.apply();
    const bool published=alias_.apply(actor,true,golden);
    update_glow(published&&held_.owns(actor));
    return published;
}
void NativeWeaponVisual::update_glow(bool visible){
    if(const auto housing=visible&&heating_?held_.housing_transform():std::nullopt){
        // Register the five small meshes together, before the first hot throw.
        for(auto& band:tip_heat_)band.prime();
        const float load=valve_frame_.load;
        vent_heat_.draw(*housing,{1.f,.018f+.065f*load,.009f+.012f*load,.18f+.69f*load});
    }else vent_heat_.hide();

    const auto shaft=visible&&tip_glow_[0]>.002f?held_.head_transform():std::nullopt;
    for(unsigned i=0;i<tip_heat_.size();++i){
        const float heat=tip_glow_[i];
        if(!shaft||heat<=.002f){tip_heat_[i].hide();continue;}
        const float brightness=heat*(.82f-.095f*float(i));
        tip_heat_[i].draw(*shaft,{1.f,.025f+.22f*heat,.012f+.033f*heat,brightness});
    }
}
std::optional<NativeWeaponVisual::ValveEmission> NativeWeaponVisual::emit_valve_smoke(unsigned actor){
    if(!live_||!valve_frame_.puffs||!held_.owns(actor)||!alias_.owns(actor))return {};
    const unsigned puffs=valve_frame_.puffs;
    valve_frame_.puffs=0;
    const auto point=held_.valve_outlet();
    if(!point)return {};
    const auto count=valve_smoke_.emit(*point,valve_frame_.load,puffs);
    if(!count)return {};
    return ValveEmission{*point,valve_frame_.scale,valve_frame_.load,count};
}
void NativeWeaponVisual::restore_equipped_preview_hand(unsigned actor){
    // The hand gadget's private data links its preview screen, which owns both
    // actors. The native Walloper pose collapses the fist to fit inside its glove.
    const unsigned data=read<unsigned>(actor+0x78);
    if(!valid(data,4))return;
    const unsigned screen=read<unsigned>(data);
    if(!valid(screen,0x58)||read<unsigned>(screen+0x54)!=actor)return;
    const unsigned preview_ratchet=read<unsigned>(screen+0x44);
    const unsigned fist=global(0x1d5dd0);
    if(!valid(preview_ratchet,256)||preview_ratchet==read<unsigned>(global(0x13f350)+0x2080)||
       read<unsigned short>(preview_ratchet+0xa6)!=0||!valid(fist,64)||
       read<unsigned>(preview_ratchet+0x64)!=fist||read<unsigned short>(fist)!=0x100)return;
    const auto scale=read<Vec3>(fist+0x20);
    if(scale.x==0.f&&scale.y==0.f&&scale.z==0.f)write(fist+0x20,Vec3{1.f,1.f,1.f});
}
bool NativeWeaponVisual::publish_preview(Preview context,unsigned actor,bool custom,bool golden,float fit){
    auto& pose=preview(context);
    // A foreign native actor can update while an acquisition actor is still
    // awaiting its draw. Releasing that actor's projection must not reset the
    // context's different pose owner. Actual menu focus/exit uses clear_preview.
    if(!custom){if(pose.owns(actor))pose.reset();return alias_.apply_preview(actor,false);}
    if(valid(actor,256)&&(read<unsigned short>(actor+0xa6)==NativeWeaponAlias::source_class||alias_.owns(actor)))
        pose.update(actor,0,Phase::Idle,0,0,0,false,true);
    const bool published=alias_.apply_preview(actor,true,golden);
    if(published&&context==Preview::Standalone)alias_.fit_preview(actor,fit);
    if(published&&context==Preview::Equipped)restore_equipped_preview_hand(actor);
    return published;
}
void NativeWeaponVisual::clear_preview(Preview context){preview(context).reset();}
std::optional<NativeWeaponVisual::EquipEvent> NativeWeaponVisual::poll_equip(){
    if(!equip_pending_||!live_)return {};
    equip_pending_=false;
    if(!held_.owns(held_identity_.actor)||!(identity(held_identity_.actor)==held_identity_))return {};
    return EquipEvent{held_identity_.actor};
}
void NativeWeaponVisual::reset(){
    clear_held();vent_heat_.reset();for(auto& band:tip_heat_)band.reset();
    held_.reset(true);standalone_.reset(true);equipped_.reset(true);acquisition_.reset(true);alias_.reset();
}
}
