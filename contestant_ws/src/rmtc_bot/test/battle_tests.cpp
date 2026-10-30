#include "rmtc_bot/battle.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
TEST(Attack,ProtectedNearestBlocksRearAndObstaclesAreNotAnInput){
  Combatant self{"A",{0,0},100,false,false,false};
  auto a=estimate_attack(self,Dir::Right,{{"B",{1,0},100,true,false,false},{"C",{2,0},100,false,false,false}}, {},false,false);
  ASSERT_EQ(a.targets.size(),1U);EXPECT_EQ(a.targets[0].seat,"B");EXPECT_DOUBLE_EQ(a.targets[0].nominal_damage,0);
}
TEST(Attack,UnrivaledIncludesDiagonalsAndReflectsPerDefender){
  Combatant self{"A",{3,3},100,false,false,true};
  auto a=estimate_attack(self,Dir::Up,{{"B",{4,4},100,false,true,false},
      {"C",{1,1},100,false,true,false},{"D",{5,5},100,false,false,true}}, {},true,true);
  ASSERT_EQ(a.targets.size(),3U);EXPECT_DOUBLE_EQ(a.reflected_damage,40);EXPECT_DOUBLE_EQ(a.targets[2].nominal_damage,100);
}
TEST(Attack,ProtectionPreventsAttackAndReflection){
  Combatant self{"A",{0,0},100,true,false,false};
  EXPECT_TRUE(estimate_attack(self,Dir::Right,{{"B",{1,0},100,false,false,false}}, {},false,false).targets.empty());
  self.protected_now=false;auto a=estimate_attack(self,Dir::Right,{{"B",{1,0},100,true,true,true}}, {},false,true);
  EXPECT_DOUBLE_EQ(a.reflected_damage,0);
}
TEST(Attack,VulnerabilityMultipliesBattleDamageOnly){
  Combatant self{"A",{0,0},100,false,false,true};
  auto a=estimate_attack(self,Dir::Right,{{"B",{1,0},100,false,true,true}}, {},false,false);
  EXPECT_DOUBLE_EQ(a.targets[0].nominal_damage,20);
  a=estimate_attack(self,Dir::Right,{{"B",{1,0},100,false,true,true}}, {},false,true);
  EXPECT_DOUBLE_EQ(a.reflected_damage,20);
}
TEST(Scoring,OverkillAndReflectionUseTotalNominalShare){
  EXPECT_DOUBLE_EQ(apportioned_effective_damage(10,20,40),5);
  EXPECT_DOUBLE_EQ(apportioned_effective_damage(100,20,40),20);
  EXPECT_DOUBLE_EQ(apportioned_effective_damage(10,20,60),10.0/3.0);
  EXPECT_DOUBLE_EQ(resource_points(120),30);EXPECT_DOUBLE_EQ(resource_points(200),30);
  EXPECT_DOUBLE_EQ(combat_points(300),9);EXPECT_DOUBLE_EQ(combat_points(500),15);EXPECT_DOUBLE_EQ(combat_points(600),15);
}
TEST(TacticalRisk,AllActionsPaySameCurrentDeathRisk){
  TacticalCandidate attack{{ActionType::Attack,Dir::Right},1,2,0,false,false};
  TacticalCandidate move{{ActionType::Move,Dir::Left},0.5,2,0,false,false};
  auto chosen=choose_tactical_candidate({attack,move},DecisionType::Balanced,1);
  ASSERT_TRUE(chosen);EXPECT_EQ(*chosen,0U);
}
TEST(TacticalRisk,DefendCanAvoidDeathAndCollectHasExtraGate){
  TacticalCandidate attack{{ActionType::Attack,Dir::Right},1,3,0,false,false};
  TacticalCandidate defend{{ActionType::Defend,{}},0,0,0,false,false};
  auto chosen=choose_tactical_candidate({attack,defend},DecisionType::Balanced,1);ASSERT_TRUE(chosen);EXPECT_EQ(*chosen,1U);
  attack.current_death_loss_points=0;chosen=choose_tactical_candidate({attack,defend},DecisionType::Collect,1);
  ASSERT_TRUE(chosen);EXPECT_EQ(*chosen,1U);attack.reliable_kill=true;
  chosen=choose_tactical_candidate({attack,defend},DecisionType::Collect,1);ASSERT_TRUE(chosen);EXPECT_EQ(*chosen,0U);
}
TEST(Missions,UsesAnnouncedCoordinateAndCompletedIDs){
  Observation o;o.tick=98;o.missions={{"far","reach",{},Cell{12,12}},{"near","reach",{},Cell{3,1}}};
  auto path=[](Cell c)->std::optional<int>{return c==Cell{3,1}?2:24;};
  auto m=nearest_unfinished_reach(o,path,100);ASSERT_TRUE(m);EXPECT_EQ(m->id,"near");
  o.completed_missions={"near"};EXPECT_FALSE(nearest_unfinished_reach(o,path,100));
}
