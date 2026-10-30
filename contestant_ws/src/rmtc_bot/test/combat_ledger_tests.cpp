#include "rmtc_bot/combat_ledger.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
TEST(CombatLedger,MutualKillKeepsBothContributionsAndBothDeathPenalties){
  std::vector<Combatant> cars{{"A",{1,1},20,false,false,false},{"B",{2,1},20,false,false,false}};
  auto out=project_combat_ledger(cars,{{0,1,20,true},{1,0,20,true}});
  ASSERT_TRUE(out.dead[0]);ASSERT_TRUE(out.dead[1]);
  EXPECT_DOUBLE_EQ(out.damage_credit[0],20);EXPECT_DOUBLE_EQ(out.kill_credit[0],60);
  EXPECT_DOUBLE_EQ(projected_effective_damage(100,out.damage_credit[0],out.kill_credit[0],out.dead[0]),120);
}
TEST(CombatLedger,OverkillIsSharedAndKillContributorsAreDeduplicated){
  std::vector<Combatant> cars{{"A",{},100,false,false,false},{"B",{},10,false,false,false},{"C",{},100,false,false,false}};
  auto out=project_combat_ledger(cars,{{0,1,10,true},{0,1,10,true},{2,1,20,true}});
  EXPECT_DOUBLE_EQ(out.damage_credit[0],5);EXPECT_DOUBLE_EQ(out.damage_credit[2],5);
  EXPECT_DOUBLE_EQ(out.kill_credit[0],30);EXPECT_DOUBLE_EQ(out.kill_credit[2],30);
}
TEST(CombatLedger,ReflectionSharesKillButDoesNotGrantOrdinaryDamage){
  std::vector<Combatant> cars{{"A",{},100,false,true,false},{"B",{},20,false,false,false},{"C",{},100,false,false,false}};
  auto out=project_combat_ledger(cars,{{0,1,20,false},{2,1,20,true}});
  EXPECT_TRUE(out.dead[1]);EXPECT_DOUBLE_EQ(out.damage_credit[0],0);
  EXPECT_DOUBLE_EQ(out.kill_credit[0],30);EXPECT_DOUBLE_EQ(out.kill_credit[2],30);
}
TEST(CombatLedger,ProtectedRespawnCannotYieldDamageOrKillRewards){
  std::vector<Combatant> cars{{"A",{},100,false,false,false},{"B",{},10,true,false,false}};
  auto out=project_combat_ledger(cars,{{0,1,100,true}});
  EXPECT_FALSE(out.dead[1]);EXPECT_DOUBLE_EQ(out.damage_credit[0],0);EXPECT_DOUBLE_EQ(out.kill_credit[0],0);
}
TEST(CombatLedger,EffectiveDamageHasAFloorButNoFiveHundredCap){
  EXPECT_DOUBLE_EQ(projected_effective_damage(10,0,0,true),0);
  EXPECT_DOUBLE_EQ(projected_effective_damage(500,20,60,false),580);
  EXPECT_DOUBLE_EQ(projected_effective_damage(520,0,0,true),460);
  EXPECT_NEAR(projected_effective_damage(100,.25,30,false),130.25,1e-10);
}
