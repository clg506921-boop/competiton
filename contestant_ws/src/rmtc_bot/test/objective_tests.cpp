#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
namespace {
Observation state(){Observation o;o.session_id="objective";o.seat="A";o.map={20,20};o.position={4,4};
  o.hp=100;o.alive=true;o.protected_now=false;o.state_complete=true;o.entities_complete=true;o.vision_radius=4;return o;}
Entity resource(Cell p,double value,std::string id="r"){Entity e;e.type=EntityType::Resource;e.position=p;e.value=value;e.id=std::move(id);return e;}
int manhattan(Cell a,Cell b){return std::abs(a.x-b.x)+std::abs(a.y-b.y);}
auto plan(const Observation& o,const std::vector<Entity>& entities,int normal=0,int high=0){
  std::unordered_map<int,Entity> resources;for(const auto& e:entities)resources[o.map.index(e.position)]=e;
  return select_navigation_objective(o,resources,normal,high,500,
    [&](Cell p)->std::optional<int>{return manhattan(o.position,p);},
    [](Cell a,Cell b)->std::optional<int>{return manhattan(a,b);});
}
}
TEST(ObjectiveCompetition,NearbyHighResourceTaskCompetesWithDistantReachInsteadOfWaitingForIt){
  auto o=state();o.missions={{"reach","reach",{},Cell{14,14}},{"high","high_resources",1,{}}};
  auto goal=plan(o,{resource({4,3},20)});ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{4,3}));
  EXPECT_DOUBLE_EQ(goal->mission_points,5);EXPECT_DOUBLE_EQ(goal->immediate_points,10);
}
TEST(ObjectiveCompetition,ConfirmedNormalProgressMakesTheLastRequiredPickupWorthFiveMissionPoints){
  auto o=state();o.missions={{"normal","normal_resources",2,{}},{"reach","reach",{},Cell{14,14}}};
  auto goal=plan(o,{resource({5,4},10)},1);ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{5,4}));
  EXPECT_DOUBLE_EQ(goal->mission_points,5);EXPECT_TRUE(o.completed_missions.empty());
}
TEST(ObjectiveReward,ColocatedReachAndResourceAreMergedWithoutLosingEitherReward){
  auto o=state();o.missions={{"reach","reach",{},Cell{5,4}},{"normal","normal_resources",1,{}}};
  auto goal=plan(o,{resource({5,4},10)});ASSERT_TRUE(goal);EXPECT_TRUE(goal->reach);EXPECT_TRUE(goal->resource);
  EXPECT_DOUBLE_EQ(goal->mission_points,10);EXPECT_DOUBLE_EQ(goal->immediate_points,12.5);
}
TEST(ObjectiveReward,TwoStopLookaheadCannotCreditOneHighMissionTwice){
  auto o=state();o.missions={{"high","high_resources",1,{}}};
  auto goal=plan(o,{resource({5,4},20,"one"),resource({6,4},20,"two")});ASSERT_TRUE(goal);
  EXPECT_LE(goal->plan_rate,6.25+1e-9);EXPECT_DOUBLE_EQ(goal->immediate_points,10);
}
TEST(ObjectiveReward,CompletedMissionIdsAndResourceCapRemoveSpentNavigationRewards){
  auto o=state();o.resource_value=120;o.missions={{"high","high_resources",1,{}},{"reach","reach",{},Cell{7,4}}};
  o.completed_missions={"high"};auto goal=plan(o,{resource({5,4},20)});ASSERT_TRUE(goal);
  EXPECT_EQ(goal->position,(Cell{7,4}));EXPECT_DOUBLE_EQ(goal->immediate_points,5);
}
TEST(ObjectiveDeadline,LastTickCannotCountAnUnreachableSecondStop){
  auto o=state();o.tick=499;o.missions={{"high","high_resources",1,{}}};
  auto goal=plan(o,{resource({5,4},20,"one"),resource({7,4},20,"two")});ASSERT_TRUE(goal);
  EXPECT_EQ(goal->position,(Cell{5,4}));EXPECT_DOUBLE_EQ(goal->plan_rate,5);
}
TEST(ObjectivePaths,UnreachablePickupDoesNotCompeteAndForwardPathFailureAddsNoBonus){
  auto o=state();o.missions={{"high","high_resources",1,{}},{"reach","reach",{},Cell{4,6}}};
  auto e=resource({5,4},20);std::unordered_map<int,Entity> resources{{o.map.index(e.position),e}};
  auto goal=select_navigation_objective(o,resources,0,0,500,[&](Cell p)->std::optional<int>{
    return p==e.position?std::nullopt:std::optional<int>(manhattan(o.position,p));},
    [](Cell,Cell)->std::optional<int>{return {};});
  ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{4,6}));EXPECT_DOUBLE_EQ(goal->plan_rate,5.0/3);
}
TEST(ObjectiveRuntime,PublicHighPickupIsChosenBeforeDistantReachWithoutPrecreditingCompletion){
  RuntimeConfig c;c.core.stage=StageMode::Battle;BotRuntime runtime(c);auto o=state();
  o.missions={{"high","high_resources",1,{}},{"reach","reach",{},Cell{14,14}}};o.entities={resource({4,3},20)};
  auto d=runtime.process(o,MonoTime{});ASSERT_TRUE(d);EXPECT_EQ(d->action.intent.direction,Dir::Up);
  EXPECT_EQ(d->reason,"P5_resource_or_collection_mission");EXPECT_TRUE(o.completed_missions.empty());
}
TEST(ObjectiveCompetition,OnlyReachRemainingAllowsNearbyScoringPickupWithLargeDeadlineSlack){
  auto o=state();o.tick=40;o.missions={{"reach","reach",{},Cell{14,14}}};
  auto goal=plan(o,{resource({5,4},20)});ASSERT_TRUE(goal);
  EXPECT_EQ(goal->position,(Cell{5,4}));EXPECT_DOUBLE_EQ(goal->mission_points,0);
  EXPECT_DOUBLE_EQ(goal->immediate_points,5);EXPECT_TRUE(o.completed_missions.empty());
}
TEST(ObjectiveDeadline,ReserveBoundaryKeepsReachAheadOfScoringDetour){
  auto o=state();o.tick=478;o.missions={{"reach","reach",{},Cell{6,4}}};
  auto goal=plan(o,{resource({4,3},20)});ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{6,4}));
}
TEST(ObjectivePaths,UnprovenOnwardRouteCannotDelayReach){
  auto o=state();o.missions={{"reach","reach",{},Cell{14,14}}};auto e=resource({5,4},20);
  auto goal=select_navigation_objective(o,{{o.map.index(e.position),e}},0,0,500,
    [&](Cell p)->std::optional<int>{return manhattan(o.position,p);},
    [](Cell,Cell)->std::optional<int>{return {};});
  ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{14,14}));
}
TEST(ObjectiveCompetition,CollectionMissionStillTakesPriorityOverUnrelatedPickup){
  auto o=state();o.missions={{"normal","normal_resources",2,{}},{"reach","reach",{},Cell{14,14}}};
  auto goal=plan(o,{resource({5,4},20,"high"),resource({4,6},10,"normal")},1,1);
  ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{4,6}));EXPECT_DOUBLE_EQ(goal->mission_points,5);
}
TEST(ObjectiveDeadline,ExactlyTwentyReserveTicksAllowsDetourButNineteenDoesNot){
  auto o=state();o.missions={{"reach","reach",{},Cell{14,14}}};o.tick=460;
  auto goal=plan(o,{resource({5,4},20)});ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{5,4}));
  o.tick=461;goal=plan(o,{resource({5,4},20)});ASSERT_TRUE(goal);EXPECT_EQ(goal->position,(Cell{14,14}));
}
TEST(ObjectiveDeadline,MultipleUnfinishedReachTargetsDoNotUseIndependentDetourProofs){
  auto o=state();o.missions={{"one","reach",{},Cell{14,14}},{"two","reach",{},Cell{14,15}}};
  auto goal=plan(o,{resource({5,4},20)});ASSERT_TRUE(goal);EXPECT_TRUE(goal->reach);
}
