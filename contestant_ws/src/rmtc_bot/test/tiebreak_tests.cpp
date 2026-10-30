#include "rmtc_bot/runtime.hpp"
#include <gtest/gtest.h>
#include <limits>
using namespace rmtc;
namespace {
TacticalCandidate candidate(ActionType action=ActionType::Wait){TacticalCandidate c;c.intent.type=action;return c;}
Observation frame(){Observation o;o.session_id="rank";o.seat="A";o.map={24,24};o.position={4,4};o.alive=true;o.hp=100;
  o.protected_now=false;o.state_complete=o.entities_complete=true;o.vision_radius=4;o.resource_value=120;o.effective_damage=500;return o;}
Entity resource(Cell p,double value){Entity e;e.type=EntityType::Resource;e.position=p;e.value=value;e.id=std::to_string(p.x)+","+std::to_string(p.y);return e;}
Entity enemy(Cell p,double hp){Entity e;e.type=EntityType::Robot;e.position=p;e.robot_id="B";e.hp=hp;e.protected_now=false;return e;}
auto goal(const Observation& o,const std::vector<Entity>& entities,int normal=0){
  std::unordered_map<int,Entity> resources;for(const auto& e:entities)resources[o.map.index(e.position)]=e;
  return select_navigation_objective(o,resources,normal,0,500,[&](Cell p)->std::optional<int>{return std::abs(p.x-o.position.x)+std::abs(p.y-o.position.y);},
    [](Cell a,Cell b)->std::optional<int>{return std::abs(a.x-b.x)+std::abs(a.y-b.y);});
}
}
TEST(TiebreakUtility,AnyStrictPrimaryAdvantageWinsBeforeSecondaryFields){
  auto points=candidate();points.score_points=.0000001;auto raw=candidate(ActionType::Move);raw.resource_value_gain=1e12;
  auto result=choose_tactical_candidate({points,raw},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,0U);
}
TEST(TiebreakUtility,CompletedMissionsThenResourceThenDamageAreComparedLexicographically){
  auto missions=candidate();missions.completed_mission_gain=1;
  auto resources=candidate(ActionType::Move);resources.resource_value_gain=200;
  auto damage=candidate(ActionType::Attack);damage.effective_damage_gain=1000;
  auto result=choose_tactical_candidate({damage,resources,missions},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,2U);
  result=choose_tactical_candidate({damage,resources},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,1U);
  result=choose_tactical_candidate({candidate(),damage},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,1U);
}
TEST(TiebreakUtility,SecondaryGainNeverCompensatesForPrimaryRiskLoss){
  auto risky=candidate(ActionType::Move);risky.resource_value_gain=1e9;risky.next_position_risk_points=.001;
  auto result=choose_tactical_candidate({risky,candidate()},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,1U);
}
TEST(TiebreakUtility,CollectStillRequiresStrictlyPositiveAttackUtility){
  auto attack=candidate(ActionType::Attack);attack.effective_damage_gain=100;attack.reliable_kill=attack.no_counter_under_confirmed_model=true;
  auto result=choose_tactical_candidate({attack,candidate()},DecisionType::Collect,1);ASSERT_TRUE(result);EXPECT_EQ(*result,1U);
}
TEST(TiebreakUtility,InvalidSecondaryValuesCannotEnterSelection){
  for(double bad:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
    auto invalid=candidate();invalid.resource_value_gain=bad;invalid.score_points=100;
    auto result=choose_tactical_candidate({invalid,candidate()},DecisionType::Balanced,1);ASSERT_TRUE(result);EXPECT_EQ(*result,1U);
  }
}
TEST(TiebreakObjective,CappedResourceStillHasRawRankValueButNoInventedScore){
  auto o=frame();auto result=goal(o,{resource({5,4},10),resource({4,3},20)});ASSERT_TRUE(result);
  EXPECT_EQ(result->position,(Cell{4,3}));EXPECT_DOUBLE_EQ(result->immediate_points,0);EXPECT_DOUBLE_EQ(result->resource_value,20);
  EXPECT_DOUBLE_EQ(result->completed_missions,0);
}
TEST(TiebreakObjective,AlreadyMetThresholdWithoutCompletedIdDoesNotGrantFiveMorePoints){
  auto o=frame();o.missions={{"normal","normal_resources",2,{}},{"value","resource_value",100,{}}};
  auto result=goal(o,{resource({5,4},10)},2);ASSERT_TRUE(result);
  EXPECT_DOUBLE_EQ(result->immediate_points,0);EXPECT_DOUBLE_EQ(result->mission_points,0);EXPECT_DOUBLE_EQ(result->completed_missions,0);
}
TEST(TiebreakObjective,ActualNextPickupCanPredictCompletionWithoutChangingObservedIds){
  auto o=frame();o.missions={{"normal","normal_resources",2,{}}};auto result=goal(o,{resource({5,4},10)},1);ASSERT_TRUE(result);
  EXPECT_DOUBLE_EQ(result->completed_missions,1);EXPECT_DOUBLE_EQ(result->immediate_points,5);EXPECT_TRUE(o.completed_missions.empty());
}
TEST(TiebreakRuntime,SafeCappedPickupIsPreferredToWait){
  for(auto mode:{DecisionType::Balanced,DecisionType::Collect}){RuntimeConfig config;config.core.stage=StageMode::Battle;config.decision_type=mode;
    BotRuntime runtime(config);auto o=frame();o.entities={resource({5,4},20)};auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);
    EXPECT_EQ(result->reason,"P5_tiebreak_resource");EXPECT_EQ(result->action.intent.type,ActionType::Move);EXPECT_EQ(result->action.intent.direction,Dir::Right);
  }
}
TEST(TiebreakRuntime,ResourcesOutrankDamageAtEqualPrimaryRiskAndUtility){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();
  o.entities={resource({4,3},20),enemy({6,4},10)};auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);
  EXPECT_EQ(result->action.intent.type,ActionType::Move);EXPECT_EQ(result->action.intent.direction,Dir::Up);
}
TEST(TiebreakRuntime,CappedAttackCanWinWhenItAddsDamageAndNoExtraPrimaryLoss){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();o.entities={enemy({6,4},10)};
  auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);EXPECT_EQ(result->action.intent.type,ActionType::Attack);
}
TEST(TiebreakRuntime,PickupThatDiesBeforeCollectionReceivesNoResourceRankCredit){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();o.hp=5;
  o.entities={resource({4,3},20),enemy({5,4},100)};auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);
  EXPECT_EQ(result->action.intent.type,ActionType::Attack);
}
TEST(TiebreakRuntime,AdditionalResourceCanJustifyANonlethalHitAfterResourceScoreCap){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();o.entities={resource({5,4},20),enemy({5,5},100)};
  auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);EXPECT_EQ(result->action.intent.direction,Dir::Right);
}
TEST(TiebreakRuntime,ResourceTiebreakDoesNotOverrideALethalDropBelowCombatCap){
  RuntimeConfig config;config.core.stage=StageMode::Battle;BotRuntime runtime(config);auto o=frame();o.hp=20;
  o.entities={resource({5,4},20),enemy({5,5},100)};auto result=runtime.process(o,MonoTime{});ASSERT_TRUE(result);
  EXPECT_NE(result->action.intent.direction,Dir::Right);
}
