// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once

#include <openrac/openrac_mod.h>
#include <array>
#include <algorithm>
#include <string>
#include <utility>

namespace revolverator {

class NativeGlowProp final {
public:
    using Transform=std::array<float,16>;

    NativeGlowProp(const openrac_api* api,openrac_mod* mod,const char* path):api_(api),mod_(mod),path_(path){}
    ~NativeGlowProp(){reset();}
    NativeGlowProp(const NativeGlowProp&)=delete;
    NativeGlowProp& operator=(const NativeGlowProp&)=delete;
    void set_path(std::string path){reset();path_=std::move(path);}

    void prime(){
        if(requested_||!mod_||api_->api_minor<12||!api_->prop_create||!api_->prop_pose||
           !api_->prop_hide||!api_->prop_remove)return;
        id_=api_->prop_create(mod_,path_.c_str());
        requested_=true;
    }

    void draw(const Transform& transform,const std::array<float,4>& tint){
        prime();
        if(id_<0)return;
        openrac_prop_pose pose{};
        pose.size=sizeof(pose);
        pose.flags=OPENRAC_PROP_MATRIX|OPENRAC_PROP_TINT;
        pose.blend=OPENRAC_PROP_ADD;
        std::copy(transform.begin(),transform.end(),pose.matrix);
        std::copy(tint.begin(),tint.end(),pose.tint);
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
    std::string path_;
    int id_=-1;
    bool visible_{},requested_{};
};

}
