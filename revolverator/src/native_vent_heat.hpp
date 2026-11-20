// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lorenzogarrone
#pragma once

#include <openrac/openrac_mod.h>
#include <algorithm>
#include <array>

namespace revolverator {

class NativeVentHeat final {
public:
    using Transform=std::array<float,16>;

    NativeVentHeat(const openrac_api* api,openrac_mod* mod):api_(api),mod_(mod){}
    ~NativeVentHeat(){reset();}
    NativeVentHeat(const NativeVentHeat&)=delete;
    NativeVentHeat& operator=(const NativeVentHeat&)=delete;

    void draw(const Transform& transform,float load){
        if(!mod_||api_->api_minor<12||!api_->prop_create||!api_->prop_pose||
           !api_->prop_hide||!api_->prop_remove)return;
        if(!requested_){
            id_=api_->prop_create(mod_,"assets/vent-heat/slots.glb");
            requested_=true;
        }
        if(id_<0)return;
        const float heat=std::clamp(load,0.f,1.f);
        openrac_prop_pose pose{};
        pose.size=sizeof(pose);
        pose.flags=OPENRAC_PROP_MATRIX|OPENRAC_PROP_TINT;
        pose.blend=OPENRAC_PROP_ADD;
        std::copy(transform.begin(),transform.end(),pose.matrix);
        pose.tint[0]=1.f;
        pose.tint[1]=.018f+.065f*heat;
        pose.tint[2]=.009f+.012f*heat;
        pose.tint[3]=.18f+.69f*heat;
        if(api_->prop_pose(mod_,id_,&pose)!=OPENRAC_HOOK_OK){hide();return;}
        if(!visible_)visible_=api_->prop_hide(mod_,id_,0)==OPENRAC_HOOK_OK;
    }

    void hide(){
        if(!visible_)return;
        api_->prop_hide(mod_,id_,1);
        visible_=false;
    }
    void reset(){
        if(id_>=0)api_->prop_remove(mod_,id_);
        id_=-1;
        visible_=requested_=false;
    }
private:
    const openrac_api* api_;
    openrac_mod* mod_;
    int id_=-1;
    bool visible_{},requested_{};
};

}
