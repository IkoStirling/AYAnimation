#include <AYAnimationEditor/HumanoidControlRig.h>
#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYTest.h>
#include <limits>
#include <nlohmann/json.hpp>
using namespace ayt::anim;
using namespace ayt::anim::editor;
using namespace ayt::resource;
using namespace ayt::math;
namespace {
struct RigFixture {
    std::shared_ptr<Skeleton> skeleton=std::make_shared<Skeleton>();
    HumanoidBoneMap mapping;
    std::vector<Float4x4> world;
    RigFixture() {
        const HumanoidBone roles[]={HumanoidBone::Hips,HumanoidBone::LeftUpperArm,HumanoidBone::LeftLowerArm,HumanoidBone::LeftHand};
        for (int i=0;i<4;++i) { Bone b; b.name=std::string(getHumanoidBoneName(roles[i])); b.parentIndex=i-1;
            b.localPosition=i==0 ? FVector3{0,0,0} : i==1 ? FVector3{0,1,0} : i==2 ? FVector3{1,1,0} : FVector3{1,-1,0};
            b.localRotation=FQuaternion::identity(); b.localScale={1,1,1}; b.inverseBindMatrix=Float4x4::identity(); skeleton->addBone(b); CHECK(mapping.bind(roles[i],i));
            const auto local=Float4x4::fromTRS(b.localPosition,b.localRotation,b.localScale); world.push_back(i ? world[i-1]*local : local);
        }
    }
};
bool near(FVector3 a,FVector3 b,float epsilon=2e-4f) { return (a-b).length()<epsilon; }
}
TEST_SUITE(HumanoidControlRigTests)
TEST_CASE(bake_samples_original_clip_endpoint_without_loop_wrapping) {
    RigFixture f; HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,f.mapping)); rig.enabled=true;
    Animation source; source.setDuration(1); source.setTicksPerSecond(1);
    AnimTrack track; track.nodeName=std::string(getHumanoidBoneName(HumanoidBone::Hips)); track.property="position";
    track.valueType=AnimTrackType::Vector3; track.times={0,1}; track.values={0,0,0,0,5,0}; source.addTrack(track);
    const auto baked=rig.bake(source,30); CHECK(baked); if(!baked) return;
    AnimationPlayer player; player.setSkeleton(f.skeleton); player.play(baked.get()); player.setLoop(false); player.setTime(1); player.evaluate();
    CHECK(near(player.getBoneWorldMatrices()[0].transformPoint({}),{0,5,0}));
}
TEST_CASE(all_four_limb_semantics_clamp_reach_and_reject_nonuniform_scale) {
    const HumanoidBone roles[4][3]={{HumanoidBone::LeftUpperArm,HumanoidBone::LeftLowerArm,HumanoidBone::LeftHand},
        {HumanoidBone::RightUpperArm,HumanoidBone::RightLowerArm,HumanoidBone::RightHand},
        {HumanoidBone::LeftUpperLeg,HumanoidBone::LeftLowerLeg,HumanoidBone::LeftFoot},
        {HumanoidBone::RightUpperLeg,HumanoidBone::RightLowerLeg,HumanoidBone::RightFoot}};
    for (unsigned limb=0;limb<4;++limb) {
        RigFixture f; HumanoidBoneMap mapping; CHECK(mapping.bind(HumanoidBone::Hips,0));
        for (int j=0;j<3;++j) CHECK(mapping.bind(roles[limb][j],j+1));
        HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,mapping)); rig.enabled=true;
        auto pose=rig.pose(); pose.limbs[limb].ik=true; pose.limbs[limb].target={100,1,0}; pose.limbs[limb].pole={0,1,3}; CHECK(rig.setPose(pose));
        HumanoidLocalPose local; std::vector<Float4x4> world; CHECK(rig.evaluate(f.world,local,world));
        const auto tip=world[3].transformPoint({}),root=world[1].transformPoint({});
        CHECK(std::fabs((tip-root).length()-2*std::sqrt(2.f))<1e-3f); CHECK(tip.x<3);
        pose.fk.scales[1]={1,2,1}; CHECK(!rig.setPose(pose));
    }
}
TEST_CASE(left_and_right_shoulders_generate_symmetric_fk_handles) {
    Skeleton skeleton; HumanoidBoneMap mapping;
    for (const auto role:{HumanoidBone::LeftShoulder,HumanoidBone::RightShoulder}) {
        Bone b; b.name=std::string(getHumanoidBoneName(role)); b.parentIndex=-1;
        b.localRotation=FQuaternion::identity(); b.localScale={1,1,1}; b.inverseBindMatrix=Float4x4::identity();
        CHECK(mapping.bind(role,int(skeleton.getBoneCount()))); skeleton.addBone(b);
    }
    HumanoidControlRig rig; CHECK(rig.bind(skeleton,mapping)); CHECK(rig.handles().size()==2);
    CHECK(rig.handles()[0].role==HumanoidBone::LeftShoulder); CHECK(rig.handles()[1].role==HumanoidBone::RightShoulder);
}
TEST_CASE(explicit_mapping_generates_partial_controls_without_source_changes) {
    RigFixture f; HumanoidControlRig rig; const auto bone=f.skeleton->getBones()[2];
    CHECK(rig.bind(*f.skeleton,f.mapping)); CHECK(rig.handles().size()==4); CHECK(rig.limbBone(RigLimb::LeftArm,2)==3);
    CHECK(rig.limbBone(RigLimb::RightLeg,0)==-1); CHECK(!rig.enabled);
    rig.enabled=true; auto p=rig.pose(); p.fk.rotations[2]=FQuaternion::fromAxisAngle({0,0,1},.3f); p.overrides[2]=true; CHECK(rig.setPose(p));
    HumanoidLocalPose local; std::vector<Float4x4> world; CHECK(rig.evaluate(f.world,local,world)); CHECK(!near(world[3].transformPoint({}),f.world[3].transformPoint({})));
    CHECK(f.skeleton->getBones()[2].localPosition.x==bone.localPosition.x); CHECK(f.skeleton->getBones()[2].localRotation.w==bone.localRotation.w);
}
TEST_CASE(ik_goal_pole_and_blend_preserve_lengths_and_tip_orientation) {
    RigFixture f; HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,f.mapping)); rig.enabled=true;
    auto p=rig.pose(); auto& c=p.limbs[0]; c.ik=true; c.target={1,2,0}; c.pole={0,2,2}; c.tipRotation=FQuaternion::fromAxisAngle({0,1,0},.4f); CHECK(rig.setPose(p));
    HumanoidLocalPose local; std::vector<Float4x4> world; CHECK(rig.evaluate(f.world,local,world));
    auto A=world[1].transformPoint({}),B=world[2].transformPoint({}),C=world[3].transformPoint({});
    CHECK(near(C,c.target)); CHECK(B.z>0); CHECK(std::fabs((B-A).length()-std::sqrt(2.f))<1e-4f); CHECK(std::fabs((C-B).length()-std::sqrt(2.f))<1e-4f);
    FVector3 position,scale; FQuaternion q; CHECK(world[3].decompose(position,q,scale)); CHECK(std::fabs(std::fabs(q.dot(c.tipRotation))-1)<1e-4f);
    p.limbs[0].pole.z=-2; CHECK(rig.setPose(p)); CHECK(rig.evaluate(f.world,local,world)); CHECK(world[2].transformPoint({}).z<0);
    p.limbs[0].weight=0; CHECK(rig.setPose(p)); CHECK(rig.evaluate(f.world,local,world)); CHECK(near(world[3].transformPoint({}),f.world[3].transformPoint({})));
    p.limbs[0].weight=.5f; CHECK(rig.setPose(p)); CHECK(rig.evaluate(f.world,local,world)); CHECK(!near(world[3].transformPoint({}),f.world[3].transformPoint({})));
}
TEST_CASE(mode_switch_matches_current_pose_in_both_directions) {
    RigFixture f; HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,f.mapping)); rig.enabled=true;
    CHECK(rig.switchLimb(RigLimb::LeftArm,true,f.world)); HumanoidLocalPose local; std::vector<Float4x4> world;
    CHECK(rig.evaluate(f.world,local,world)); for (int i=0;i<4;++i) CHECK(near(world[i].transformPoint({}),f.world[i].transformPoint({})));
    auto p=rig.pose(); p.limbs[0].target={1,1.5f,.2f}; CHECK(rig.setPose(p)); CHECK(rig.evaluate(f.world,local,world)); const auto displayed=world;
    CHECK(rig.switchLimb(RigLimb::LeftArm,false,displayed)); CHECK(rig.evaluate(f.world,local,world));
    for (int i=0;i<4;++i) { CHECK(near(world[i].transformPoint({}),displayed[i].transformPoint({}))); FVector3 a,b; FQuaternion q,r;
        world[i].decompose(a,q,b); displayed[i].decompose(a,r,b); CHECK(std::fabs(std::fabs(q.dot(r))-1)<1e-4f); }
}
TEST_CASE(invalid_state_mapping_and_binding_reload_are_atomic) {
    RigFixture f; HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,f.mapping)); const auto original=rig.encode();
    auto p=rig.pose(); p.limbs[0].target.x=std::numeric_limits<float>::infinity(); CHECK(!rig.setPose(p)); CHECK(rig.encode()==original);
    p=rig.pose(); p.limbs[1].ik=true; CHECK(!rig.setPose(p));
    auto badMap=f.mapping; CHECK(badMap.bind(HumanoidBone::Head,1)); CHECK(!rig.bind(*f.skeleton,badMap)); CHECK(rig.encode()==original);
    auto value=nlohmann::json::parse(original); value["binding"][0][0]="changed"; CHECK(!rig.decode(value.dump())); CHECK(rig.encode()==original);
    value=nlohmann::json::parse(original); value["pose"]["limbs"][0][1]=2; CHECK(!rig.decode(value.dump())); CHECK(rig.encode()==original);
    CHECK(!rig.decode("garbage")); CHECK(!rig.moveKeys({0},.1,2));
}
TEST_CASE(controller_keys_sample_save_restore_and_bake_formal_clip) {
    RigFixture f; HumanoidControlRig rig; CHECK(rig.bind(*f.skeleton,f.mapping)); rig.enabled=true;
    CHECK(rig.captureFK(f.world)); CHECK(rig.record(0,1));
    auto p=rig.pose(); p.fk.positions[0]={0,1,0}; p.fk.rotations[2]=FQuaternion::fromAxisAngle({0,0,1},.4f); CHECK(rig.setPose(p)); CHECK(rig.record(1,1));
    CHECK(near(rig.sample(.5).fk.positions[0],{0,.5f,0})); auto second=rig; CHECK(second.decode(rig.encode())); CHECK(second.keys().size()==2);
    HumanoidControlRig restored; CHECK(restored.restore(*f.skeleton,rig.encode())); CHECK(restored.encode()==rig.encode());
    CHECK(!second.moveKeys({1},-1,1)); CHECK(second.keys()[1].seconds==1); CHECK(second.moveKeys({1},-.25,1)); CHECK(second.seek(.5));
    Animation source; source.setName("test"); source.setDuration(1); source.setTicksPerSecond(30); source.addNotify({"step",.5,2});
    const auto baked=rig.bake(source,10); CHECK(baked); if (!baked) return;
    CHECK(baked->getNotifyCount()==1); CHECK(baked->getTrackCount()==12); CHECK(source.getTrackCount()==0);
    AnimationPlayer player; player.setSkeleton(f.skeleton); player.play(baked.get()); player.setTime(.5); player.evaluate();
    CHECK(near(player.getBoneWorldMatrices()[0].transformPoint({}),{0,.5f,0}));
    CHECK(!rig.bake(source,std::numeric_limits<double>::infinity()));
}
TEST_SUITE_END
