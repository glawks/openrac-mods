// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_arm.hpp"
#include "attachment.hpp"
#include "rotor_controller.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace revolverator {
namespace {
constexpr Quaternion identity{0,0,0,1};
constexpr unsigned torso_offset=320,torso_joints=3;
constexpr float release_duration=.25f;
// In Ratchet's frame the shaft (-Y) rises 68 degrees, with the rear valve
// facing back rather than sideways. The same grip feeds the throw handoff.
constexpr Vec3 raised_grip_angles{-1.18682389f,0.f,1.570796327f};
Vec3 position(const std::array<float,16>& matrix) { return {matrix[12],matrix[13],matrix[14]}; }
Vec3 axis(const std::array<float,16>& matrix,unsigned row) {
    return unit({matrix[row*4],matrix[row*4+1],matrix[row*4+2]});
}
Vec3 local_vector(const std::array<float,16>& frame,Vec3 world) {
    return {dot(world,axis(frame,0)),dot(world,axis(frame,1)),dot(world,axis(frame,2))};
}
Vec3 world_vector(const std::array<float,16>& frame,Vec3 local) {
    return unit(axis(frame,0)*local.x+axis(frame,1)*local.y+axis(frame,2)*local.z);
}
Vec3 cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 grip_angles(Vec3 x,Vec3 y,Vec3 z) {
    const float pitch=std::asin(std::clamp(-x.z,-1.f,1.f));
    return std::abs(x.z)>.99999f?Vec3{0,pitch,std::atan2(-y.x,y.y)}:
        Vec3{std::atan2(y.z,z.z),pitch,std::atan2(x.y,x.x)};
}
Quaternion conjugate(Quaternion q){return {-q[0],-q[1],-q[2],q[3]};}
Quaternion blend_grip(Quaternion from,Quaternion to,float weight){
    return multiply(from,blend_rotation(multiply(conjugate(from),to),weight));
}
Vec3 blend_direction(Vec3 from,Vec3 to,float weight){
    return rotate(blend_rotation(shortest_arc(from,to),weight),from);
}
Quaternion product(Quaternion a,Quaternion b) {
    Quaternion q{a[3]*b[0]+b[3]*a[0]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]+b[3]*a[1]+a[2]*b[0]-a[0]*b[2],
        a[3]*b[2]+b[3]*a[2]+a[0]*b[1]-a[1]*b[0],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    float norm=0;for(float v:q)norm+=v*v;
    if(!(norm>.000001f)||!std::isfinite(norm))return identity;
    for(auto& v:q)v/=std::sqrt(norm);
    return q;
}
Quaternion delta_rotation(Vec3 v) {
    const float angle=std::sqrt(length_squared(v));
    if(angle<.000001f)return identity;
    const float s=std::sin(angle*.5f)/angle;
    return {v.x*s,v.y*s,v.z*s,std::cos(angle*.5f)};
}
struct Stack {
    const openrac_api* api; unsigned saved{},address{};
    explicit Stack(const openrac_api* a):api(a),address(a->stack_push(64,&saved)){}
    ~Stack(){if(address)api->stack_pop(saved);}
};
}
NativeArm::NativeArm(const openrac_api* api,GuestArena& arena):Memory(api),guest_(arena),
    bone_(function<Bone>("FUN_0020cca8")),basis_(function<Basis>("FUN_001fa030")) {}
NativeArm::~NativeArm(){reset();}
void NativeArm::set_head_scale(float radial,float axial) {
    head_radial_=std::isfinite(radial)?std::clamp(radial,1.f,RotorController::maximum_radial_scale):1.f;
    head_axial_=std::isfinite(axial)?std::clamp(axial,1.f,RotorController::maximum_axial_scale):1.f;
}
bool NativeArm::attached() const {
    return owner_&&api_->loaded_level()==level_&&valid(owner_,256)&&
        read<unsigned>(owner_+0x24)==klass_&&read<unsigned>(owner_+0x38)==stamp_;
}
bool NativeArm::attach(unsigned owner) {
    if(owner_==owner&&attached())return true;
    reset();
    const auto klass=read<unsigned>(owner+0x24);
    if(!valid(owner,256)||!valid(klass,0x30)||read<unsigned char>(klass+8)!=111)return false;
    if(arena_level_!=api_->loaded_level()){arena_=0;arena_level_=api_->loaded_level();}
    if(!arena_)arena_=guest_.allocate(512);
    if(!valid(arena_,512)){arena_=0;return false;}
    std::memset(api_->memory+arena_,0,512);
    owner_=owner;klass_=klass;stamp_=read<unsigned>(owner+0x38);level_=api_->loaded_level();
    // Joint 52 is the clavicle. Leave it untouched; rotate the actual upper arm
    // (53) and forearm (54) independently around a deliberately outward elbow.
    for(unsigned i=0;i<3;++i){
        const unsigned node=arena_+i*64;
        write<unsigned char>(node+1,1);write(node+4,0x70000000u+(i==2?56:53+i)*64);
        write(node+8,read<unsigned>(owner+0x64));write(node+0x10,identity);
        write(node+0x20,Vec3{1,1,1});write(owner+0x64,node);
    }
    // The lower spine (3..5) is above the pelvis and below both clavicles.
    // Use the unused tail of our arena; leave root, legs and authored keys alone.
    for(unsigned i=0;i<torso_joints;++i){
        const unsigned node=arena_+torso_offset+i*64;
        write<unsigned char>(node+1,1);write(node+4,0x70000000u+(3+i)*64);
        write(node+8,read<unsigned>(owner+0x64));write(node+0x10,identity);
        write(node+0x20,Vec3{1,1,1});write(owner+0x64,node);
    }
    return true;
}
std::optional<NativeArm::Matrix> NativeArm::sample(unsigned joint) {
    if(!attached()||!((joint<=6)||(joint>=52&&joint<=56)))return {};
    const unsigned original=read<unsigned>(klass_+0x1c),table=arena_+192,chain=arena_+272;
    if(!valid(original,68))return {};
    std::memcpy(api_->memory+table,api_->memory+original,68);
    const unsigned count=joint<=6?joint+1:8+(joint-52);
    write<unsigned char>(chain,static_cast<unsigned char>(count));
    write<unsigned short>(chain+2,1);
    for(unsigned i=0;i<std::min(count,7u);++i)write<unsigned char>(chain+4+i,static_cast<unsigned char>(i));
    if(joint>=52)for(unsigned i=52;i<=joint;++i)write<unsigned char>(chain+11+i-52,static_cast<unsigned char>(i));
    write<unsigned char>(chain+4+count,static_cast<unsigned char>(joint));
    write(table+4,chain);
    Stack stack(api_);if(!stack.address)return {};
    // BuildBoneTransform accepts a chain index, not a joint. Supply a private,
    // temporary chain for this synchronous call; restore the class before any
    // update/render callback can observe it. All other chain entries are copied.
    struct Restore {
        NativeArm& self;unsigned at,old,replacement;
        ~Restore(){if(self.read<unsigned>(at)==replacement)self.write(at,old);}
    } restore{*this,klass_+0x1c,original,table};
    write(klass_+0x1c,table);
    bone_(owner_,0,stack.address);
    Matrix matrix{};std::memcpy(matrix.data(),api_->memory+stack.address,64);
    for(float value:matrix)if(!std::isfinite(value))return {};
    return matrix;
}
std::optional<NativeArm::Matrix> NativeArm::parent_frame(unsigned joint) {
    const auto rest=read<unsigned>(klass_+0x18);
    if(!valid(rest,joint*16+16))return {};
    const auto parent=read<unsigned>(rest+joint*16+12);
    if(parent<0x70000000u||(parent&63u))return {};
    return sample((parent-0x70000000u)/64);
}
bool NativeArm::aim(unsigned node,unsigned joint,unsigned endpoint,Vec3 target,float weight) {
    write(node+0x10,identity);
    const auto base=sample(joint),end=sample(endpoint),parent=parent_frame(joint);
    if(!base||!end||!parent)return false;
    const auto origin=position(*base);
    // Native mode-0 modifiers compose q_authored * q_modifier. Its row-vector
    // matrices therefore apply the correction in the PARENT frame, not in the
    // posed joint frame. Quaternion rows also turn opposite to shortest_arc's
    // active-vector convention, hence the deliberately reversed arguments.
    const auto rotation=shortest_arc(local_vector(*parent,target-origin),
        local_vector(*parent,position(*end)-origin));
    Quaternion best=identity;float score=length_squared(position(*end)-target);
    write(node+0x10,rotation);
    if(const auto result=sample(endpoint))if(length_squared(position(*result)-target)<score)best=rotation;
    rotations_[(node-arena_)/64]=best;write(node+0x10,blend_rotation(best,weight));
    return true;
}
bool NativeArm::turn_wrist(float weight,Vec3 shaft,Vec3 front) {
    const unsigned node=arena_+128;
    write(node+0x10,identity);const auto hand=sample(56),parent=parent_frame(56);
    if(!hand||!parent||!grip_ready_)return false;
    using Axes=std::array<Vec3,2>;
    const Axes goal{shaft,front};
    const auto grip_axes=[&](const Matrix& frame)->Axes{
        return {world_vector(frame,grip_axes_[1]*-1.f),world_vector(frame,grip_axes_[0])};
    };
    const auto error=[&](const Matrix& frame){
        const auto axes=grip_axes(frame);
        return length_squared(goal[0]-axes[0])+length_squared(goal[1]-axes[1]);
    };
    const auto grip_rotation=[&](const Axes& axes){
        const auto y=unit(local_vector(*parent,axes[0]*-1.f));
        const auto z=unit(cross(local_vector(*parent,axes[1]),y));
        return rotation_from_euler(grip_angles(unit(cross(y,z)),y,z));
    };
    // Use both axes to pin wrist roll relative to Ratchet's facing. A shaft
    // direction alone leaves the hand free to twist while moving or turning.
    const auto desired=grip_rotation(goal);
    const auto candidate=multiply(grip_rotation(grip_axes(*hand)),
        {-desired[0],-desired[1],-desired[2],desired[3]});
    Quaternion best=identity;
    Matrix measured=*hand;
    float score=error(measured);
    write(node+0x10,candidate);
    if(const auto posed=sample(56))if(const float trial=error(*posed);trial<score){
        score=trial;best=candidate;measured=*posed;
    }
    write(node+0x10,rotations_[2]);
    if(const auto posed=sample(56))if(const float trial=error(*posed);trial<score){
        score=trial;best=rotations_[2];measured=*posed;
    }
    // Native keys are not always unit quaternions after their authored blend.
    // Consequently their composed rows can be sheared: a purely algebraic fit
    // can leave a visible residual. Refine against measured native matrices,
    // using a damped 3-axis Jacobian, with a strict bounded work budget.
    for(unsigned iteration=0;iteration<5&&score>.000001f;++iteration){
        const auto current=grip_axes(measured);std::array<Axes,3> jacobian{};
        bool complete=true;
        for(unsigned i=0;i<3;++i){
            Vec3 step{};if(i==0)step.x=.002f;else if(i==1)step.y=.002f;else step.z=.002f;
            write(node+0x10,product(best,delta_rotation(step)));
            const auto trial=sample(56);if(!trial){complete=false;break;}
            const auto axes=grip_axes(*trial);
            for(unsigned a=0;a<axes.size();++a)jacobian[i][a]=(axes[a]-current[a])*500.f;
        }
        if(!complete)break;
        float system[3][4]{};
        for(unsigned r=0;r<3;++r){
            for(unsigned c=0;c<3;++c)system[r][c]=dot(jacobian[r][0],jacobian[c][0])+
                dot(jacobian[r][1],jacobian[c][1])+(r==c?.0001f:0);
            system[r][3]=dot(jacobian[r][0],goal[0]-current[0])+dot(jacobian[r][1],goal[1]-current[1]);
        }
        for(unsigned col=0;col<3;++col){
            unsigned pivot=col;for(unsigned r=col+1;r<3;++r)if(std::abs(system[r][col])>std::abs(system[pivot][col]))pivot=r;
            for(unsigned c=col;c<4;++c)std::swap(system[col][c],system[pivot][c]);
            const float scale=system[col][col];
            if(std::abs(scale)<.0000001f){complete=false;break;}
            for(unsigned c=col;c<4;++c)system[col][c]/=scale;
            for(unsigned r=0;r<3;++r)if(r!=col){
                const float factor=system[r][col];
                for(unsigned c=col;c<4;++c)system[r][c]-=factor*system[col][c];
            }
        }
        if(!complete)break;
        Vec3 step{system[0][3],system[1][3],system[2][3]};
        if(!finite(step))break;
        const float magnitude=std::sqrt(length_squared(step));if(magnitude>.6f)step=step*(.6f/magnitude);
        bool improved=false;
        for(unsigned attempt=0;attempt<4;++attempt){
            const auto next=product(best,delta_rotation(step));
            write(node+0x10,next);const auto posed=sample(56);if(!posed)break;
            const float trial=error(*posed);
            if(trial<score){score=trial;best=next;measured=*posed;improved=true;break;}
            step=step*.5f;
        }
        if(!improved)break;
    }
    rotations_[2]=best;write(node+0x10,blend_rotation(best,weight));return true;
}
bool NativeArm::track_weapon(unsigned weapon) {
    if(weapon_==weapon&&read<unsigned>(weapon+0x24)==weapon_class_&&read<unsigned>(weapon+0x38)==weapon_stamp_)return true;
    if(weapon_)return false;
    const auto klass=read<unsigned>(weapon+0x24);
    if(!valid(weapon,256)||!valid(klass,48)||read<unsigned char>(klass+8)!=5)return false;
    weapon_=weapon;weapon_class_=klass;weapon_stamp_=read<unsigned>(weapon+0x38);
    original_radius_=read<float>(weapon+12);owned_radius_=std::max(original_radius_,1280.f);
    // NativeRotor exclusively owns the weapon's modifier list and neutral root.
    // The arm owns Ratchet's arm/spine modifiers and the held bounds radius.
    return true;
}
std::optional<Vec3> NativeArm::hold(unsigned owner,unsigned weapon,float yaw,float seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>.1f||!std::isfinite(yaw)||!attach(owner)||!valid(weapon,256))return {};
    if(!track_weapon(weapon))return {};
    native_strike_=false;throw_progress_=0;
    release_started_=false;release_time_=0;raised_time_=std::min(.32f,raised_time_+seconds);yaw_=yaw;
    return align_weapon(weapon);
}
std::optional<Vec3> NativeArm::throw_pose(unsigned weapon,float progress) {
    if(!native_strike_||!std::isfinite(progress))return {};
    throw_progress_=std::clamp(progress,0.f,1.f);
    return align_weapon(weapon);
}
void NativeArm::begin_native_strike(){native_strike_=true;throw_progress_=0;}
bool NativeArm::pose() {
    for(unsigned i=0;i<3;++i)write(arena_+i*64+0x10,identity);
    for(unsigned i=0;i<torso_joints;++i)write(arena_+torso_offset+i*64+0x10,identity);
    const auto forward_axis=unit(read<Vec3>(owner_+0xc0));
    const float yaw=length_squared(forward_axis)>.5f?std::atan2(forward_axis.y,forward_axis.x):yaw_;
    const Vec3 outward{std::sin(yaw),-std::cos(yaw),0},forward{std::cos(yaw),std::sin(yaw),0};
    const auto facing=rotation_from_euler({0,0,yaw});
    const float progress=std::clamp(1.f-release_time_/release_duration,0.f,1.f);
    // Solve the arm after the spine: the hand stays attached to its new shoulder
    // position, including when authored animation advances later in the frame.
    if(releasing()&&!native_strike_&&!pose_torso(progress,outward))return false;
    if(native_strike_&&throw_progress_>=1.f)return grip_ready_;
    const auto shoulder=sample(53),elbow=sample(54),hand=sample(56);
    if(!shoulder||!elbow||!hand)return false;
    if(!grip_ready_){
        for(unsigned i=0;i<3;++i){
            const auto direction=unit(read<Vec3>(weapon_+0xc0+i*16));
            grip_axes_[i]=local_vector(*hand,direction);
        }
        const auto inverse=conjugate(facing);
        const auto upper=position(*elbow)-position(*shoulder);
        const auto reach=unit(position(*hand)-position(*shoulder));
        const auto bend=unit(upper-reach*dot(upper,reach));
        lift_hand_=rotate(inverse,position(*hand)-read<Vec3>(owner_+0x10));
        lift_bend_=length_squared(bend)>.5f?rotate(inverse,bend):Vec3{0,-1,0};
        const auto grip=rotation_from_euler(grip_angles(unit(read<Vec3>(weapon_+0xc0)),
            unit(read<Vec3>(weapon_+0xd0)),unit(read<Vec3>(weapon_+0xe0))));
        lift_grip_=multiply(inverse,grip);
        grip_ready_=true;
    }
    const float upper=std::sqrt(length_squared(position(*elbow)-position(*shoulder))),
        lower=std::sqrt(length_squared(position(*hand)-position(*elbow)));
    const auto goal=releasing()&&!native_strike_?throw_goal(position(*shoulder),upper,lower,outward,forward,progress):
        arm_goal(position(*shoulder),upper,lower,outward,forward,1);
    if(!goal)return false;
    if(!native_strike_&&!releasing())return pose_lift(*goal,position(*shoulder),upper,lower,facing);
    const float t=std::clamp(raised_time_/.2f,0.f,1.f);
    const float strike=throw_progress_*throw_progress_*(3-2*throw_progress_);
    const float follow=native_strike_?1.f-strike:
        releasing()?std::clamp((1.f-progress)/.35f,0.f,1.f):1.f;
    const float weight=t*t*(3-2*t)*follow;
    if(!aim(arena_,53,54,goal->elbow,weight)||!aim(arena_+64,54,56,goal->hand,weight))return false;
    const float wrist=std::clamp((raised_time_-.08f)/.18f,0.f,1.f);
    const float wrist_weight=wrist*wrist*(3-2*wrist)*follow;
    if(native_strike_){
        const auto grip=multiply(facing,rotation_from_euler(raised_grip_angles));
        return turn_wrist(wrist_weight,rotate(grip,{0,-1,0}),rotate(grip,{1,0,0}));
    }
    const float sweep=releasing()?std::clamp(progress/.55f,0.f,1.f):0.f;
    const Vec3 up{0,0,1};
    return turn_wrist(wrist_weight,unit(up*(1-sweep)+forward*sweep),
        unit(forward*(1-sweep)-up*sweep));
}
bool NativeArm::pose_lift(const ArmGoal& goal,Vec3 shoulder,float upper,float lower,Quaternion facing){
    const float t=std::clamp(raised_time_/.25f,0.f,1.f);
    const float weight=t*t*(3.f-2.f*t);
    // The wrist follows a path relative to Ratchet, independent of the sharp
    // shoulder motion in the first native punch keys. Solve the elbow for the
    // same target so the hand and drill stay attached to the real arm.
    const auto start=read<Vec3>(owner_+0x10)+rotate(facing,lift_hand_);
    const auto bend=rotate(facing,blend_direction(lift_bend_,{0,-1,0},weight));
    const auto reach=goal.hand-start;
    // Bring the hand forward from the cocked pose before lifting it.
    const float draw=std::min(t/.86f,1.f);
    const float rise=std::max((t-.46f)/.54f,0.f);
    const float draw_weight=draw*draw*(3.f-2.f*draw);
    const float rise_weight=rise*rise*(3.f-2.f*rise);
    // Give the forearm room to pass beside the torso during the forward draw.
    const float arc=std::clamp((t-.27f)/.58f,0.f,1.f);
    const float clearance=12.8f*lower*arc*arc*(1.f-arc)*(1.f-arc);
    const auto hand=start+Vec3{reach.x*draw_weight,reach.y*draw_weight,reach.z*rise_weight}+
        rotate(facing,{0,-1,0})*clearance;
    const auto target=arm_goal_for_hand(shoulder,upper,lower,bend,hand);
    if(!target||!aim(arena_,53,54,target->elbow,1.f)||!aim(arena_+64,54,56,target->hand,1.f))return false;

    const auto grip=multiply(facing,blend_grip(lift_grip_,rotation_from_euler(raised_grip_angles),weight));
    return turn_wrist(1.f,rotate(grip,{0,-1,0}),rotate(grip,{1,0,0}));
}
bool NativeArm::pose_torso(float progress,Vec3 outward) {
    const float angle=throw_lean(progress);
    if(angle==0)return true;
    constexpr float weights[]{.3f,.4f,.3f};
    for(unsigned i=0;i<torso_joints;++i){
        const auto parent=parent_frame(3+i);if(!parent)return false;
        // Native modifier quaternions rotate opposite to active world vectors.
        // The outward/right axis therefore bends the upper body forward.
        const auto local_axis=unit(local_vector(*parent,outward));
        if(length_squared(local_axis)<.5f)return false;
        write(arena_+torso_offset+i*64+0x10,delta_rotation(local_axis*(angle*weights[i])));
    }
    return true;
}
std::optional<Vec3> NativeArm::align_weapon(unsigned weapon) {
    if(!attached()||weapon!=weapon_||!valid(weapon,256)||read<unsigned>(weapon+0x24)!=weapon_class_||
        read<unsigned>(weapon+0x38)!=weapon_stamp_)return {};
    // The authored animation advances after mods::frame. Re-solve against its
    // current keys here, inside Moby180.update before the render snapshot, so
    // a modifier computed for the previous frame cannot tilt the held shaft.
    if(!pose())return {};
    const auto wrist=sample(56);if(!wrist)return {};
    auto at=position(*wrist);
    const Vec3 x=unit({(*wrist)[0],(*wrist)[1],(*wrist)[2]}),
        y=unit({(*wrist)[4],(*wrist)[5],(*wrist)[6]}),z=unit({(*wrist)[8],(*wrist)[9],(*wrist)[10]});
    const auto axis=[&](Vec3 local){return unit(x*local.x+y*local.y+z*local.z);};
    // Authored quaternion interpolation may leave a slightly sheared joint
    // basis. A held rigid weapon needs an orthonormal basis before Euler
    // extraction; keep the solved shaft (Y) exact and remove only its shear.
    const auto iy=axis(grip_axes_[1]);
    auto ix=unit(axis(grip_axes_[0])-iy*dot(axis(grip_axes_[0]),iy));
    if(length_squared(ix)<.5f)ix=unit(cross(iy,axis(grip_axes_[2])));
    const auto iz=unit(cross(ix,iy));ix=unit(cross(iy,iz));
    auto angles=grip_angles(ix,iy,iz);
    if(native_strike_&&releasing()&&release_time_<release_duration){
        // The held-item update has placed the weapon at the game's socket for
        // these same animation keys. Hand control back over the recovery.
        const auto native_at=read<Vec3>(weapon+0x10);
        const auto native_x=unit(read<Vec3>(weapon+0xc0)),native_y=unit(read<Vec3>(weapon+0xd0)),
            native_z=unit(read<Vec3>(weapon+0xe0));
        if(finite(native_at)&&length_squared(native_x)>.5f&&length_squared(native_y)>.5f&&length_squared(native_z)>.5f){
            const float t=1.f-release_time_/release_duration;
            const float weight=t*t*(3.f-2.f*t);
            const auto from=rotation_from_euler(angles);
            const auto to=rotation_from_euler(grip_angles(native_x,native_y,native_z));
            angles=rotation_to_euler(blend_grip(from,to,weight));
            at=at+(native_at-at)*weight;
        }
    }
    write(weapon+0x10,at);
    // The bounds center is in 1/1024 units, while position is in metres.
    write(weapon,at*1024.f);
    write(weapon+12,owned_radius_);
    write(weapon+0x40,angles);
    basis_(weapon+0xc0,weapon+0x40);
    // Actual Hoven-model shaft axis and tip, including its offset from the grip.
    return at+unit(read<Vec3>(weapon+0xc0))*.013592358f
        -unit(read<Vec3>(weapon+0xd0))*(.903450852f*head_axial_)+unit(read<Vec3>(weapon+0xe0))*.203153592f;
}
void NativeArm::release(){
    if(!attached())return;
    release_time_=release_duration;
    release_started_=true;
    if(native_strike_)throw_progress_=1;
}
void NativeArm::settle(float seconds) {
    if(release_time_<=0){reset();return;}
    if(!attached()){reset();return;}
    if(release_started_){release_started_=false;return;}
    release_time_=std::max(0.f,release_time_-std::clamp(seconds,0.f,.1f));
    if(release_time_<=0)reset();else pose();
}
void NativeArm::unlink(unsigned owner,unsigned node) {
    unsigned link=owner+0x64;
    for(unsigned i=0;i<32;++i){
        const auto at=read<unsigned>(link);
        if(at==node){write(link,read<unsigned>(node+8));return;}
        if(!valid(at,64))return;
        link=at+8;
    }
}
void NativeArm::reset(bool discard) {
    if(attached()){
        for(unsigned i=0;i<3;++i)unlink(owner_,arena_+i*64);
        for(unsigned i=0;i<torso_joints;++i)unlink(owner_,arena_+torso_offset+i*64);
    }
        if(api_->loaded_level()==level_&&weapon_&&valid(weapon_,256)&&read<unsigned>(weapon_+0x24)==weapon_class_&&read<unsigned>(weapon_+0x38)==weapon_stamp_){
            if(read<float>(weapon_+12)==owned_radius_)write(weapon_+12,original_radius_);
        }
    if(discard||api_->loaded_level()!=arena_level_){arena_=0;arena_level_=-1;}
    owner_=klass_=stamp_=weapon_=weapon_class_=weapon_stamp_=0;level_=-1;raised_time_=release_time_=yaw_=0;
    native_strike_=release_started_=false;throw_progress_=0;
    rotations_={identity,identity,identity};grip_ready_=false;grip_axes_={};original_radius_=owned_radius_=0;
}
}
